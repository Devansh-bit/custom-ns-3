import torch
import torch.nn as nn
import numpy as np
from sklearn.model_selection import KFold
from scipy.stats import uniform
import random
import os
import json
from PIL import Image
from torch.utils.data import Dataset, DataLoader
from CNN.dutycycle_config import TECH_START_IDX, TECH_TO_BINS

class DeepSpectrumModel(nn.Module):
    """
    Generic DeepSpectrum model that can use any CNN backbone (e.g., EfficientNet, ResNet, MobileNet).
    Supports both callable constructors (like torchvision.models.resnet18)
    and ready model instances, as well as pretrained weights.
    """

    def __init__(self, model, pretrained=True, weights=None, weights_path=None, device=None):
        super().__init__()

        # --- Load / assign backbone ---
        if callable(model):
            # Torchvision >= 0.13 uses weights argument instead of pretrained
            try:
                self.backbone = model(weights=weights if weights is not None else ("IMAGENET1K_V1" if pretrained else None))
            except TypeError:
                # For older models that still use `pretrained=...`
                self.backbone = model(pretrained=pretrained)
        else:
            # User passed an already-instantiated model
            self.backbone = model

        # --- Convert to grayscale (1-channel) ---
        self._convert_first_conv_to_single_channel()

        # --- Remove classifier layers if present ---
        self._remove_classifier()

        # --- Infer output feature dimension ---
        backbone_out_features = self._infer_backbone_out_features()

        # --- Define detection head ---
        self.nm = 72
        self.no = 4 * self.nm  # (conf, x, w) for each

        self.head = nn.Sequential(
            nn.AdaptiveAvgPool2d(1),
            nn.Flatten(),
            nn.Linear(backbone_out_features, 256),
            nn.ReLU(inplace=True),
            nn.Dropout(0.3),
            nn.Linear(256, 128),
            nn.ReLU(inplace=True),
            nn.Dropout(0.2),
            nn.Linear(128, self.no)
        )

        # --- Load weights if provided ---
        if weights_path is not None and os.path.exists(weights_path):
            map_location = device if device is not None else (torch.device('cuda') if torch.cuda.is_available() else torch.device('cpu'))
            state_dict = torch.load(weights_path, map_location=map_location)
            
            # Check if checkpoint was trained with different nm (number of anchors)
            # by inspecting the final linear layer shape
            checkpoint_nm = None
            if 'head.8.weight' in state_dict:
                checkpoint_out_features = state_dict['head.8.weight'].shape[0]
                checkpoint_nm = checkpoint_out_features // 4  # no = 4 * nm
            
            if checkpoint_nm is not None and checkpoint_nm != self.nm:
                print(f"Note: Checkpoint was trained with nm={checkpoint_nm}, current model has nm={self.nm}")
                print(f"Rebuilding head to match checkpoint architecture...")
                
                # Rebuild head to match checkpoint
                self.nm = checkpoint_nm
                self.no = 4 * self.nm
                self.head = nn.Sequential(
                    nn.AdaptiveAvgPool2d(1),
                    nn.Flatten(),
                    nn.Linear(backbone_out_features, 256),
                    nn.ReLU(inplace=True),
                    nn.Dropout(0.3),
                    nn.Linear(256, 128),
                    nn.ReLU(inplace=True),
                    nn.Dropout(0.2),
                    nn.Linear(128, self.no)
                )
            
            self.load_state_dict(state_dict)
            print(f"Loaded model weights from {weights_path} (nm={self.nm})")

    def _convert_first_conv_to_single_channel(self):
        """Modify the first convolution layer to accept 1-channel grayscale input."""
        for name, module in self.backbone.named_modules():
            if isinstance(module, nn.Conv2d) and module.in_channels == 3:
                new_conv = nn.Conv2d(
                    1,
                    module.out_channels,
                    kernel_size=module.kernel_size,
                    stride=module.stride,
                    padding=module.padding,
                    bias=module.bias is not None
                )

                with torch.no_grad():
                    new_conv.weight[:] = module.weight.mean(dim=1, keepdim=True)
                self._replace_module(name, new_conv)
                break

    def _replace_module(self, name, new_module):
        """Replace a submodule by name (handles nested models)."""
        parts = name.split('.')
        obj = self.backbone
        for p in parts[:-1]:
            obj = getattr(obj, p)
        setattr(obj, parts[-1], new_module)

    def _remove_classifier(self):
        """Remove final classification head if present."""
        if hasattr(self.backbone, "classifier"):
            self.backbone.classifier = nn.Identity()
        elif hasattr(self.backbone, "fc"):
            self.backbone.fc = nn.Identity()

    def _infer_backbone_out_features(self):
        """Infer backbone output dimension by running a dummy forward pass."""
        self.backbone.eval()
        dummy = torch.zeros(1, 1, 256, 256)  # CHANGED to 256x256
        with torch.no_grad():
            try:
                features = self.backbone.features(dummy)
            except AttributeError:
                features = self.backbone(dummy)
        return features.shape[1]

    # --- Forward pass ---
    def forward(self, x):
        if hasattr(self.backbone, "features"):
            features = self.backbone.features(x)
        else:
            features = self.backbone(x)

        out = self.head(features)
        batch_size = out.shape[0]
        predictions = out.view(batch_size, self.nm, 4)
        predictions = torch.sigmoid(predictions) 
        return predictions


class DeepSpectrumLoss(nn.Module):
    """Loss function for DeepSpectrum"""
    def __init__(self, lambda_x=5.0, lambda_w=5.0, lambda_noobj=0.05, lambda_obj=5.0, lambda_t=5.0):
        super().__init__()
        self.lambda_x = lambda_x
        self.lambda_w = lambda_w
        self.lambda_noobj = lambda_noobj
        self.lambda_obj = lambda_obj
        self.lambda_t = lambda_t
        
    def forward(self, predictions, targets):
        """
        Args:
            predictions: [B, Nm, 4] - [c,x,w,t]
            targets: [B, Nm, 3] -  [c,x,w]
        Returns:
            total_loss, loss_dict
        """
        pred_x= predictions[..., 1]
        pred_w= predictions[..., 2]
        pred_c= predictions[..., 0]
        pred_t = predictions[..., 3]

        target_x = targets[..., 1]
        target_w = targets[..., 2]
        target_c = targets[..., 0]
        target_t = targets[..., 3]
        has_obj = targets[..., 0]
        
        obj_mask = has_obj > 0
        noobj_mask = has_obj == 0
        
        # Compute losses
        loss_x = self.lambda_x * (obj_mask * (pred_x - target_x) ** 2).sum() / (obj_mask.sum() + 1e-6)
        loss_w = self.lambda_w * (obj_mask * (pred_w - target_w) ** 2).sum() / (obj_mask.sum() + 1e-6)
        # For the 4th column (duration / duty_cycle) only account the loss over zigbee rows.
        # The incoming predictions have shape [B, Nm, 4]. Use TECH_START_IDX and TECH_TO_BINS
        # to determine the zigbee slice and compute MSE over that slice (across batch).
        with torch.no_grad():
            Nm = pred_t.shape[1]
        device = predictions.device
        # Build zigbee mask per-anchor and combine with object mask so that the
        # 4th-column loss is computed only where both (a) anchor belongs to Zigbee
        # and (b) an object is present in the target (object mask).
        start_idx = int(TECH_START_IDX.get('zigbee', 14))
        bins = int(TECH_TO_BINS.get('zigbee', 16))
        end_idx = start_idx + bins
        idxs = torch.arange(Nm, device=device)
        zigbee_mask = (idxs >= start_idx) & (idxs < end_idx)  # shape: (Nm,)
        # Expand to batch shape
        zigbee_mask = zigbee_mask.unsqueeze(0).expand(pred_t.shape[0], -1)

        # Combine with object mask (only compute duty/duration loss where object present)
        combined_mask = obj_mask & zigbee_mask

        loss_t = self.lambda_t * (combined_mask * (pred_t - target_t) ** 2).sum() / (combined_mask.sum() + 1e-6)

        import torch.nn.functional as F

        loss_noobj = self.lambda_noobj * F.binary_cross_entropy(pred_c[noobj_mask], target_c[noobj_mask], reduction='mean')
        loss_obj   = self.lambda_obj   * F.binary_cross_entropy(pred_c[obj_mask],   target_c[obj_mask],   reduction='mean')

        total_loss = loss_x + loss_w + loss_noobj + loss_obj + loss_t

        loss_dict = {
            'loss_x': loss_x.item(),
            'loss_w': loss_w.item(),
            'loss_noobj': loss_noobj.item(),
            'loss_obj': loss_obj.item(),
            'loss_t': loss_t.item(),
            'total': total_loss.item()
        }
        
        return total_loss, loss_dict


class DeepSpectrum:
    """Complete DeepSpectrum system"""
    def __init__(self, device='cuda', pretrained_weights_path=None, lambda_x=5.0, lambda_w=5.0, lambda_noobj=0.05, lambda_obj=5.0, lambda_t=5.0):
        self.device = torch.device(device if torch.cuda.is_available() else 'cpu')
        print("The device used: ", self.device)

        # Initialize models
        try:
            import torchvision.models as tv_models
            backbone_ctor = tv_models.mobilenet_v2
            self.master_model = DeepSpectrumModel(
                backbone_ctor,
                pretrained=True,
                weights_path=pretrained_weights_path,
                device=self.device
            ).to(self.device)
            print("Backbone model created successfully.")
        except Exception as e:
            print(f"Warning: could not create default backbone: {e}. Please set DeepSpectrum.master_model to a valid DeepSpectrumModel instance.")
            self.master_model = None

        self.master_loss = DeepSpectrumLoss(lambda_x=lambda_x, lambda_w=lambda_w, lambda_noobj=lambda_noobj, lambda_obj=lambda_obj, lambda_t=lambda_t)
        self.confidence_threshold = 0.5
        self.sampling_rate = 20e6  # 20 MHz
        self.center_freq = 2.437e9  # 2.437 GHz
        self.bandwidth = 20e6  # 20 MHz

        print("Model Loaded")
        
    def train_master(self, train_loader, val_loader, epochs=50, lr_head=0.001, lr_backbone=1e-5):
        """Train the master model. Returns lists of train and validation losses per epoch."""
        param_groups = [
        {'params': self.master_model.head.parameters(), 'lr': lr_head},
        {'params': self.master_model.backbone.parameters(), 'lr': lr_backbone}
        ]
        optimizer = torch.optim.Adam(param_groups)
        scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, epochs)
        self.master_model.train()

        print("Starting training...")
        
        # Track losses
        train_losses = []
        val_losses = []

        for epoch in range(epochs):
            total_loss = 0
            for batch_idx, (images, targets) in enumerate(train_loader):
                images = images.to(self.device)
                targets = targets.to(self.device)
                predictions = self.master_model(images)
                loss, loss_dict = self.master_loss(predictions, targets)
                loss.backward()
                torch.nn.utils.clip_grad_norm_(self.master_model.parameters(), max_norm=5.0)
                optimizer.step()
                optimizer.zero_grad()
                total_loss += loss.item()
                if batch_idx % 5 == 0:
                    print(f'Epoch {epoch}, Batch {batch_idx}, Loss: {loss.item():.4f}')
            scheduler.step()
            
            # Calculate average training loss
            avg_train_loss = total_loss / len(train_loader)
            train_losses.append(avg_train_loss)
            
            self.master_model.eval()
            val_loss = 0
            with torch.no_grad():
                for images, targets in val_loader:
                    images = images.to(self.device)
                    targets = targets.to(self.device)
                    predictions = self.master_model(images)
                    loss, _ = self.master_loss(predictions, targets)
                    val_loss += loss.item()
            
            # Calculate average validation loss
            avg_val_loss = val_loss / len(val_loader)
            val_losses.append(avg_val_loss)
            
            print(f'Epoch {epoch}: Train Loss={avg_train_loss:.4f}, Val Loss={avg_val_loss:.4f}')
            self.master_model.train()
            if (epoch + 1) % 5 == 0:
                torch.save(self.master_model.state_dict(), f'master_epoch{epoch+1}.pt')
        
        return train_losses, val_losses
    def predict(self, spectrogram):
        """
        Predict raw model outputs from spectrogram.
        Args:
            spectrogram: numpy array [H, W] or torch tensor [B, 1, H, W] or [1, H, W] or [H, W]
        Returns:
            raw_predictions: torch tensor of shape [B, Nm, 3] where 3 = [x_center, width, confidence]
                            Values are in normalized range [0, 1] for use in R², precision calculations
        """
        if self.master_model is None:
            raise RuntimeError('master_model not set in DeepSpectrum instance')
        self.master_model.eval()
        # Prepare input
        if isinstance(spectrogram, torch.Tensor):
            if len(spectrogram.shape) == 2:
                spectrogram = spectrogram.unsqueeze(0).unsqueeze(0)
            elif len(spectrogram.shape) == 3:
                spectrogram = spectrogram.unsqueeze(0)
        else:
            spectrogram = torch.from_numpy(spectrogram).float()
            if len(spectrogram.shape) == 2:
                spectrogram = spectrogram.unsqueeze(0).unsqueeze(0)
        spectrogram = spectrogram.to(self.device)
        with torch.no_grad():
            predictions = self.master_model(spectrogram)
        return predictions  # Return raw tensor [B, Nm, 3]
    
    def predict_json(self, spectrogram):
        """
        Predict and return JSON-formatted detections.
        Args:
            spectrogram: numpy array [H, W] or torch tensor
        Returns:
            detections: list of detection dictionaries in JSON format
        """
        raw_predictions = self.predict(spectrogram)
        detections = self._decode_predictions(raw_predictions)
        return detections
    
    def _decode_predictions(self, predictions):
        """
        Decode model output to proper JSON format with unnormalized frequencies.
        Args:
            predictions: torch tensor [B, Nm, 4] or numpy array [Nm, 4] or [B, Nm, 4]
        Returns:
            detections: list of detection dictionaries with proper JSON format
        """
        # Convert to numpy if needed
        if isinstance(predictions, torch.Tensor):
            predictions = predictions.cpu().numpy()

        # Handle batch dimension: if shape is [B, Nm, 4], take first batch item
        if len(predictions.shape) == 4:
            predictions = predictions[0]  # Take first item from batch
        
        detections = []
        
        for i, pred in enumerate(predictions):
            x_center, width, confidence = pred[:3]  # Get all 3 values
            
            if confidence < self.confidence_threshold:
                continue
            
            # Determine signal type based on anchor index
            if 0 <= i < 14:
                signal_type = 'wifi'
            elif 14 <= i < 30:
                signal_type = 'zigbee'
            elif 30 <= i < 31:
                signal_type = 'cordless'
            elif 31 <= i < 32:
                signal_type = 'microwave'
            else: 
                signal_type = 'bluetooth'
            
            # Unnormalize center frequency: map from [0, 1] to [2400, 2500] MHz
            # x_center is normalized position in cell [0, 1]
            cell_width = self.bandwidth / 16  # Width of each frequency cell
            start_freq = self.center_freq - self.bandwidth / 2  # Start frequency of spectrum
            
            # Calculate which cell this anchor belongs to
            cell_idx = i % 16
            cell_origin = start_freq + cell_idx * cell_width
            
            # Denormalize center frequency: x_center [0,1] -> actual position in cell
            fc_unnorm = cell_origin + x_center * cell_width
            
            # Denormalize bandwidth: width is normalized [0, 1]
            bw_hz = width * self.bandwidth  # Multiply by full bandwidth
            
            # Determine band based on frequency
            band = self._get_band(fc_unnorm)
            
            detection = {
                'technology': signal_type,
                'center_frequency_hz': float(fc_unnorm),
                'center_frequency_mhz': float(fc_unnorm / 1e6),
                'bandwidth_hz': float(bw_hz),
                'bandwidth_mhz': float(bw_hz / 1e6),
                'band': band,
                'confidence': float(confidence)
            }
            detections.append(detection)
        
        return detections
    def _get_band(self, fc_hz):
        """
        Determine frequency band from center frequency.
        Args:
            fc_hz: center frequency in Hz
        Returns:
            band string (e.g., '2.4GHz', '5GHz')
        """
        if 2.4e9 <= fc_hz < 2.5e9:
            return '2.4GHz'
        elif 5.0e9 <= fc_hz < 6.0e9:
            return '5GHz'
        elif 900e6 <= fc_hz < 1000e6:
            return '900MHz'
        elif 433e6 <= fc_hz < 435e6:
            return '433MHz'
        else:
            return 'Unknown'
    
    def _get_channel(self, signal_type, fc):
        """Calculate channel number from frequency"""
        if signal_type == 'wifi':
            return int((fc - 2.407e9) / 5e6)
        elif signal_type == 'zigbee':
            return int((fc - 2.400e9) / 5e6) + 10
        elif signal_type == 'bluetooth':
            return int((fc - 2.401e9) / 1e6)
        return None  # LoRa has flexible frequency
    def save_models(self, master_path='master_model.pt', aux_path='aux_model.pt'):
        """Save model weights"""
        if self.master_model is not None:
            torch.save(self.master_model.state_dict(), master_path)
        if hasattr(self, 'auxiliary_model') and getattr(self, 'auxiliary_model') is not None:
            torch.save(self.auxiliary_model.state_dict(), aux_path)
        print(f"Models saved to {master_path} and {aux_path}")
    def load_models(self, master_path='master_model.pt', aux_path='aux_model.pt'):
        """Load model weights"""
        if os.path.exists(master_path):
            self.master_model.load_state_dict(torch.load(master_path))
        else:
            raise FileNotFoundError(f"Master model file not found: {master_path}")
        if hasattr(self, 'auxiliary_model') and os.path.exists(aux_path):
            self.auxiliary_model.load_state_dict(torch.load(aux_path))
            print(f"Models loaded from {master_path} and {aux_path}")
        else:
            print(f"Master model loaded from {master_path}. Aux model not loaded (missing or not instantiated)")

class SpectrogramFolderDataset(Dataset):
    """Dataset that reads images from a folder and loads batch target tensors from a .npy file.
    Expects images named like image1.jpg, image2.jpg, ... and a .npy file with shape (N, 72, 3).
    Returns (image_tensor [1,H,W], target_tensor [72,3])
    """
    def __init__(self, images_dir, tensor_batch_path, transform=None, image_size=(256,256)):
        self.images_dir = images_dir
        self.transform = transform
        self.image_size = image_size
        self.tensor_batch = np.load(tensor_batch_path)  # shape: (N, 72, 4)
        files = sorted([f for f in os.listdir(images_dir) if f.lower().endswith(('.png', '.jpg', '.jpeg'))])
        self.samples = files[:self.tensor_batch.shape[0]]  # Ensure same length as tensor batch
    def __len__(self):
        return len(self.samples)
    def __getitem__(self, idx):
        fname = self.samples[idx]
        path = os.path.join(self.images_dir, fname)
        img = Image.open(path).convert('L')
        img = img.resize(self.image_size, Image.BILINEAR)
        img_arr = np.array(img, dtype=np.float32) / 255.0
        img_tensor = torch.from_numpy(img_arr).unsqueeze(0)
        target_tensor = torch.from_numpy(self.tensor_batch[idx]).float()  # shape: (72, 4)
        return img_tensor, target_tensor

# import torch
# import torch.nn as nn
# import numpy as np
# from sklearn.model_selection import KFold
# from scipy.stats import uniform
# import random
# import os
# import json
# from PIL import Image
# from torch.utils.data import Dataset, DataLoader
# from dutycycle_config import TECH_START_IDX, TECH_TO_BINS

# class DeepSpectrumModel(nn.Module):
#     """
#     Generic DeepSpectrum model that can use any CNN backbone (e.g., EfficientNet, ResNet, MobileNet).
#     Supports both callable constructors (like torchvision.models.resnet18)
#     and ready model instances, as well as pretrained weights.
#     """

#     def __init__(self, model, pretrained=True, weights=None, weights_path=None, device=None):
#         super().__init__()

#         # --- Load / assign backbone ---
#         if callable(model):
#             # Torchvision >= 0.13 uses weights argument instead of pretrained
#             try:
#                 self.backbone = model(weights=weights if weights is not None else ("IMAGENET1K_V1" if pretrained else None))
#             except TypeError:
#                 # For older models that still use `pretrained=...`
#                 self.backbone = model(pretrained=pretrained)
#         else:
#             # User passed an already-instantiated model
#             self.backbone = model

#         # --- Convert to grayscale (1-channel) ---
#         self._convert_first_conv_to_single_channel()

#         # --- Remove classifier layers if present ---
#         self._remove_classifier()

#         # --- Infer output feature dimension ---
#         backbone_out_features = self._infer_backbone_out_features()

#         # --- Define detection head ---
#         self.nm = 15
#         self.no = 4 * self.nm  # (conf, x, w) for each

#         self.head = nn.Sequential(
#             nn.AdaptiveAvgPool2d(1),
#             nn.Flatten(),
#             nn.Linear(backbone_out_features, 256),
#             nn.ReLU(inplace=True),
#             nn.Dropout(0.5),
#             nn.Linear(256, 128),
#             nn.ReLU(inplace=True),
#             nn.Dropout(0.2),
#             nn.Linear(128, self.no)
#         )

#         # --- Load weights if provided ---
#         if weights_path is not None and os.path.exists(weights_path):
#             map_location = device if device is not None else (torch.device('cuda') if torch.cuda.is_available() else torch.device('cpu'))
#             self.load_state_dict(torch.load(weights_path, map_location=map_location))
#             print(f"Loaded model weights from {weights_path}")

#     def _convert_first_conv_to_single_channel(self):
#         """Modify the first convolution layer to accept 1-channel grayscale input."""
#         for name, module in self.backbone.named_modules():
#             if isinstance(module, nn.Conv2d) and module.in_channels == 3:
#                 new_conv = nn.Conv2d(
#                     1,
#                     module.out_channels,
#                     kernel_size=module.kernel_size,
#                     stride=module.stride,
#                     padding=module.padding,
#                     bias=module.bias is not None
#                 )

#                 with torch.no_grad():
#                     new_conv.weight[:] = module.weight.mean(dim=1, keepdim=True)
#                 self._replace_module(name, new_conv)
#                 break

#     def _replace_module(self, name, new_module):
#         """Replace a submodule by name (handles nested models)."""
#         parts = name.split('.')
#         obj = self.backbone
#         for p in parts[:-1]:
#             obj = getattr(obj, p)
#         setattr(obj, parts[-1], new_module)

#     def _remove_classifier(self):
#         """Remove final classification head if present."""
#         if hasattr(self.backbone, "classifier"):
#             self.backbone.classifier = nn.Identity()
#         elif hasattr(self.backbone, "fc"):
#             self.backbone.fc = nn.Identity()

#     def _infer_backbone_out_features(self):
#         """Infer backbone output dimension by running a dummy forward pass."""
#         self.backbone.eval()
#         dummy = torch.zeros(1, 1, 256, 256)  # CHANGED to 256x256
#         with torch.no_grad():
#             try:
#                 features = self.backbone.features(dummy)
#             except AttributeError:
#                 features = self.backbone(dummy)
#         return features.shape[1]

#     # --- Forward pass ---
#     def forward(self, x):
#         if hasattr(self.backbone, "features"):
#             features = self.backbone.features(x)
#         else:
#             features = self.backbone(x)

#         out = self.head(features)
#         batch_size = out.shape[0]
#         predictions = out.view(batch_size, self.nm, 4)
#         predictions = torch.sigmoid(predictions) 
#         return predictions


# class DeepSpectrumLoss(nn.Module):
#     """Loss function for DeepSpectrum"""
#     def __init__(self, lambda_x=5.0, lambda_w=5.0, lambda_noobj=0.05, lambda_obj=5.0, lambda_t=5.0):
#         super().__init__()
#         self.lambda_x = lambda_x
#         self.lambda_w = lambda_w
#         self.lambda_noobj = lambda_noobj
#         self.lambda_obj = lambda_obj
#         self.lambda_t = lambda_t
        
#     def forward(self, predictions, targets):
#         """
#         Args:
#             predictions: [B, Nm, 4] - [c,x,w,t]
#             targets: [B, Nm, 3] -  [c,x,w]
#         Returns:
#             total_loss, loss_dict
#         """
#         pred_x= predictions[..., 1]
#         pred_w= predictions[..., 2]
#         pred_c= predictions[..., 0]
#         pred_t = predictions[..., 3]

#         target_x = targets[..., 1]
#         target_w = targets[..., 2]
#         target_c = targets[..., 0]
#         target_t = targets[..., 3]
#         has_obj = targets[..., 0]
        
#         obj_mask = has_obj > 0
#         noobj_mask = has_obj == 0
        
#         # Compute losses
#         loss_x = self.lambda_x * (obj_mask * (pred_x - target_x) ** 2).sum() / (obj_mask.sum() + 1e-6)
#         loss_w = self.lambda_w * (obj_mask * (pred_w - target_w) ** 2).sum() / (obj_mask.sum() + 1e-6)
#         # For the 4th column (duration / duty_cycle) only account the loss over zigbee rows.
#         # The incoming predictions have shape [B, Nm, 4]. Use TECH_START_IDX and TECH_TO_BINS
#         # to determine the zigbee slice and compute MSE over that slice (across batch).
#         with torch.no_grad():
#             Nm = pred_t.shape[1]
#         device = predictions.device
#         # index tensor for anchors 0..Nm-1
#         idxs = torch.arange(Nm, device=device)
       
#         # Zigbee-duration/duty MSE (kept as before, computed where objects present)
#         loss_t = self.lambda_t * (obj_mask * (pred_t - target_t) ** 2).sum() / (obj_mask.sum() + 1e-6)

#         import torch.nn.functional as F

#         loss_noobj = self.lambda_noobj * F.binary_cross_entropy(pred_c[noobj_mask], target_c[noobj_mask], reduction='mean')
#         loss_obj   = self.lambda_obj   * F.binary_cross_entropy(pred_c[obj_mask],   target_c[obj_mask],   reduction='mean')

#         # Radar-specific loss: for the 9th anchor (index 8) if present, compute an
#         # additional loss combining MSE on the 4th column and BCE on confidence.

#         total_loss = loss_x + loss_w + loss_noobj + loss_obj + loss_t 

#         loss_dict = {
#             'loss_x': loss_x.item(),
#             'loss_w': loss_w.item(),
#             'loss_noobj': loss_noobj.item(),
#             'loss_obj': loss_obj.item(),
#             'loss_t': loss_t.item(),
#             'total': total_loss.item()
#         }
        
#         return total_loss, loss_dict


# class DeepSpectrum:
#     """Complete DeepSpectrum system"""
#     def __init__(self, device='cuda', pretrained_weights_path=None, lambda_x=5.0, lambda_w=5.0, lambda_noobj=0.05, lambda_obj=5.0, lambda_t=5.0):
#         self.device = torch.device(device if torch.cuda.is_available() else 'cpu')
#         print("The device used: ", self.device)

#         # Initialize models
#         try:
#             import torchvision.models as tv_models
#             backbone_ctor = tv_models.mobilenet_v2
#             self.master_model = DeepSpectrumModel(
#                 backbone_ctor,
#                 pretrained=True,
#                 weights_path=pretrained_weights_path,
#                 device=self.device
#             ).to(self.device)
#         except Exception:
#             print("Warning: could not create default backbone. Please set DeepSpectrum.master_model to a valid DeepSpectrumModel instance.")

#         self.master_loss = DeepSpectrumLoss(lambda_x=lambda_x, lambda_w=lambda_w, lambda_noobj=lambda_noobj, lambda_obj=lambda_obj, lambda_t=lambda_t)
#         self.confidence_threshold = 0.5
#         self.sampling_rate = 20e6  # 20 MHz
#         self.center_freq = 2.437e9  # 2.437 GHz
#         self.bandwidth = 20e6  # 20 MHz

#         print("Model Loaded")
        
#     def train_master(self, train_loader, val_loader, epochs=50, lr_head=0.001, lr_backbone=1e-5, weight_decay=1e-4):
#         """Train the master model. Returns lists of train and validation losses per epoch."""

#         param_groups = [
#         {'params': self.master_model.head.parameters(), 'lr': lr_head, 'weight_decay': weight_decay},
#         {'params': self.master_model.backbone.parameters(), 'lr': lr_backbone, 'weight_decay': weight_decay}
#     ]
#         optimizer = torch.optim.Adam(param_groups)
#         scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, epochs)
#         self.master_model.train()

#         print("Starting training...")
        
#         # Track losses
#         train_losses = []
#         val_losses = []

#         for epoch in range(epochs):
#             total_loss = 0
#             for batch_idx, (images, targets) in enumerate(train_loader):
#                 images = images.to(self.device)
#                 targets = targets.to(self.device)
#                 predictions = self.master_model(images)
#                 loss, loss_dict = self.master_loss(predictions, targets)
#                 loss.backward()
#                 torch.nn.utils.clip_grad_norm_(self.master_model.parameters(), max_norm=5.0)
#                 optimizer.step()
#                 optimizer.zero_grad()
#                 total_loss += loss.item()
#                 if batch_idx % 5 == 0:
#                     print(f'Epoch {epoch}, Batch {batch_idx}, Loss: {loss.item():.4f}')
#             scheduler.step()
            
#             # Calculate average training loss
#             avg_train_loss = total_loss / len(train_loader)
#             train_losses.append(avg_train_loss)
            
#             self.master_model.eval()
#             val_loss = 0
#             with torch.no_grad():
#                 for images, targets in val_loader:
#                     images = images.to(self.device)
#                     targets = targets.to(self.device)
#                     predictions = self.master_model(images)
#                     loss, _ = self.master_loss(predictions, targets)
#                     val_loss += loss.item()
            
#             # Calculate average validation loss
#             avg_val_loss = val_loss / len(val_loader)
#             val_losses.append(avg_val_loss)
            
#             print(f'Epoch {epoch}: Train Loss={avg_train_loss:.4f}, Val Loss={avg_val_loss:.4f}')
#             self.master_model.train()
#             if (epoch + 1) % 5 == 0:
#                 torch.save(self.master_model.state_dict(), f'master_epoch{epoch+1}.pt')
        
#         return train_losses, val_losses
#     def predict(self, spectrogram):
#         """
#         Predict raw model outputs from spectrogram.
#         Args:
#             spectrogram: numpy array [H, W] or torch tensor [B, 1, H, W] or [1, H, W] or [H, W]
#         Returns:
#             raw_predictions: torch tensor of shape [B, Nm, 3] where 3 = [x_center, width, confidence]
#                             Values are in normalized range [0, 1] for use in R², precision calculations
#         """
#         if self.master_model is None:
#             raise RuntimeError('master_model not set in DeepSpectrum instance')
#         self.master_model.eval()
#         # Prepare input
#         if isinstance(spectrogram, torch.Tensor):
#             if len(spectrogram.shape) == 2:
#                 spectrogram = spectrogram.unsqueeze(0).unsqueeze(0)
#             elif len(spectrogram.shape) == 3:
#                 spectrogram = spectrogram.unsqueeze(0)
#         else:
#             spectrogram = torch.from_numpy(spectrogram).float()
#             if len(spectrogram.shape) == 2:
#                 spectrogram = spectrogram.unsqueeze(0).unsqueeze(0)
#         spectrogram = spectrogram.to(self.device)
#         with torch.no_grad():
#             predictions = self.master_model(spectrogram)
#         return predictions  # Return raw tensor [B, Nm, 3]
    
#     def predict_json(self, spectrogram):
#         """
#         Predict and return JSON-formatted detections.
#         Args:
#             spectrogram: numpy array [H, W] or torch tensor
#         Returns:
#             detections: list of detection dictionaries in JSON format
#         """
#         raw_predictions = self.predict(spectrogram)
#         detections = self._decode_predictions(raw_predictions)
#         return detections
    
#     def _decode_predictions(self, predictions):
#         """
#         Decode model output to proper JSON format with unnormalized frequencies.
#         Args:
#             predictions: torch tensor [B, Nm, 4] or numpy array [Nm, 4] or [B, Nm, 4]
#         Returns:
#             detections: list of detection dictionaries with proper JSON format
#         """
#         # Convert to numpy if needed
#         if isinstance(predictions, torch.Tensor):
#             predictions = predictions.cpu().numpy()

#         # Handle batch dimension: if shape is [B, Nm, 4], take first batch item
#         if len(predictions.shape) == 4:
#             predictions = predictions[0]  # Take first item from batch
        
#         detections = []
        
#         for i, pred in enumerate(predictions):
#             x_center, width, confidence = pred[:3]  # Get all 3 values
            
#             if confidence < self.confidence_threshold:
#                 continue
            
#             # Determine signal type based on anchor index
#             if 0 <= i < 14:
#                 signal_type = 'wifi'
#             elif 14 <= i < 30:
#                 signal_type = 'zigbee'
#             elif 30 <= i < 31:
#                 signal_type = 'cordless'
#             elif 31 <= i < 32:
#                 signal_type = 'microwave'
#             else: 
#                 signal_type = 'bluetooth'
            
#             # Unnormalize center frequency: map from [0, 1] to [2400, 2500] MHz
#             # x_center is normalized position in cell [0, 1]
#             cell_width = self.bandwidth / 16  # Width of each frequency cell
#             start_freq = self.center_freq - self.bandwidth / 2  # Start frequency of spectrum
            
#             # Calculate which cell this anchor belongs to
#             cell_idx = i % 16
#             cell_origin = start_freq + cell_idx * cell_width
            
#             # Denormalize center frequency: x_center [0,1] -> actual position in cell
#             fc_unnorm = cell_origin + x_center * cell_width
            
#             # Denormalize bandwidth: width is normalized [0, 1]
#             bw_hz = width * self.bandwidth  # Multiply by full bandwidth
            
#             # Determine band based on frequency
#             band = self._get_band(fc_unnorm)
            
#             detection = {
#                 'technology': signal_type,
#                 'center_frequency_hz': float(fc_unnorm),
#                 'center_frequency_mhz': float(fc_unnorm / 1e6),
#                 'bandwidth_hz': float(bw_hz),
#                 'bandwidth_mhz': float(bw_hz / 1e6),
#                 'band': band,
#                 'confidence': float(confidence)
#             }
#             detections.append(detection)
        
#         return detections
#     def _get_band(self, fc_hz):
#         """
#         Determine frequency band from center frequency.
#         Args:
#             fc_hz: center frequency in Hz
#         Returns:
#             band string (e.g., '2.4GHz', '5GHz')
#         """
#         if 2.4e9 <= fc_hz < 2.5e9:
#             return '2.4GHz'
#         elif 5.0e9 <= fc_hz < 6.0e9:
#             return '5GHz'
#         elif 900e6 <= fc_hz < 1000e6:
#             return '900MHz'
#         elif 433e6 <= fc_hz < 435e6:
#             return '433MHz'
#         else:
#             return 'Unknown'
    
#     def _get_channel(self, signal_type, fc):
#         """Calculate channel number from frequency"""
#         if signal_type == 'wifi':
#             return int((fc - 2.407e9) / 5e6)
#         elif signal_type == 'zigbee':
#             return int((fc - 2.400e9) / 5e6) + 10
#         elif signal_type == 'bluetooth':
#             return int((fc - 2.401e9) / 1e6)
#         return None  # LoRa has flexible frequency
#     def save_models(self, master_path='master_model.pt', aux_path='aux_model.pt'):
#         """Save model weights"""
#         if self.master_model is not None:
#             torch.save(self.master_model.state_dict(), master_path)
#         if hasattr(self, 'auxiliary_model') and getattr(self, 'auxiliary_model') is not None:
#             torch.save(self.auxiliary_model.state_dict(), aux_path)
#         print(f"Models saved to {master_path} and {aux_path}")
#     def load_models(self, master_path='master_model.pt', aux_path='aux_model.pt'):
#         """Load model weights"""
#         if os.path.exists(master_path):
#             self.master_model.load_state_dict(torch.load(master_path))
#         else:
#             raise FileNotFoundError(f"Master model file not found: {master_path}")
#         if hasattr(self, 'auxiliary_model') and os.path.exists(aux_path):
#             self.auxiliary_model.load_state_dict(torch.load(aux_path))
#             print(f"Models loaded from {master_path} and {aux_path}")
#         else:
#             print(f"Master model loaded from {master_path}. Aux model not loaded (missing or not instantiated)")

# class SpectrogramFolderDataset(Dataset):
#     """Dataset that reads images from a folder and loads batch target tensors from a .npy file.
#     Expects images named like image1.jpg, image2.jpg, ... and a .npy file with shape (N, 72, 3).
#     Returns (image_tensor [1,H,W], target_tensor [72,3])
#     """
#     def __init__(self, images_dir, tensor_batch_path, transform=None, image_size=(256,256)):
#         self.images_dir = images_dir
#         self.transform = transform
#         self.image_size = image_size
#         self.tensor_batch = np.load(tensor_batch_path)  # shape: (N, 72, 4)
#         files = sorted([f for f in os.listdir(images_dir) if f.lower().endswith(('.png', '.jpg', '.jpeg'))])
#         self.samples = files[:self.tensor_batch.shape[0]]  # Ensure same length as tensor batch
#     def __len__(self):
#         return len(self.samples)
#     def __getitem__(self, idx):
#         fname = self.samples[idx]
#         path = os.path.join(self.images_dir, fname)
#         img = Image.open(path).convert('L')
#         img = img.resize(self.image_size, Image.BILINEAR)
#         img_arr = np.array(img, dtype=np.float32) / 255.0
#         img_tensor = torch.from_numpy(img_arr).unsqueeze(0)
#         target_tensor = torch.from_numpy(self.tensor_batch[idx]).float()  # shape: (72, 4)
#         return img_tensor, target_tensor
